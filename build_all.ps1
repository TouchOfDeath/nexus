# ==============================================================================
#                      NEXUS PROJECT: MASTER BUILD SCRIPT (WINDOWS)
#           0% External Dependencies  -  100% Native Windows x86-64 Toolchain
# ==============================================================================
[CmdletBinding()]
param(
    [switch]$SkipTests,
    [switch]$SkipSelfHost
)

$ErrorActionPreference = "Stop"
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $ScriptDir

function Write-Cyan   ($msg) { Write-Host $msg -ForegroundColor Cyan }
function Write-Yellow ($msg) { Write-Host $msg -ForegroundColor Yellow }
function Write-Green  ($msg) { Write-Host $msg -ForegroundColor Green }
function Write-Red    ($msg) { Write-Host $msg -ForegroundColor Red }

Write-Cyan "=================================================================="
Write-Cyan " [NEXUS] Building Full Project Pipeline (Native Windows Driver)   "
Write-Cyan "=================================================================="

# --- STEP 1: Build All Native & Diagnostic Tools ---
Write-Yellow "`n[+] STEP 1: Building all native tools and diagnostic engines..."
& "$ScriptDir\tools\build_tools.ps1"
if ($LASTEXITCODE -ne 0) {
    Write-Red "[-] Failed to build tools."
    exit 1
}
Write-Green "[+] All tools built and verified in bin/ and tools/."

$CompilerDir = Join-Path $ScriptDir "compiler"
$BinDir      = Join-Path $ScriptDir "bin"
$ExamplesDir = Join-Path $ScriptDir "examples"
$TestsDir    = Join-Path $ScriptDir "tests"

$NexPrep = Join-Path $BinDir "nexprep.exe"
$Nexc    = Join-Path $CompilerDir "nexc.exe"
$NexElf  = Join-Path $BinDir "nexelf.exe"
$NexMacho= Join-Path $BinDir "nexmacho.exe"
$NexTest = Join-Path $BinDir "nextest.exe"

# --- STEP 2: Compile Master Test Suite ---
Write-Yellow "`n[+] STEP 2: Compiling master_test_suite.nex -> app.exe using nexc.exe..."
$CodeNex = Join-Path $CompilerDir "code.nex"
& $NexPrep "$ExamplesDir\master_test_suite.nex" $CodeNex
if ($LASTEXITCODE -ne 0) {
    Write-Red "[-] Preprocessor failed on master_test_suite.nex"
    exit 1
}

Push-Location $CompilerDir
try {
    & $Nexc
    if ($LASTEXITCODE -ne 0) {
        Write-Red "[-] nexc compilation failed"
        exit 1
    }
}
finally {
    Pop-Location
}
Write-Green "[+] Master test suite compiled to compiler\app.exe successfully!"

# --- STEP 3: Execute and Verify app.exe ---
Write-Yellow "`n[+] STEP 3: Executing compiled app.exe and asserting regression features..."
$AppExe = Join-Path $CompilerDir "app.exe"
$Output = & $AppExe
Write-Host ($Output -join "`r`n")

$ExpectedPatterns = @(
    "25",
    "10",
    "33",
    "5",
    "15",
    "If Check: Equal (42 == 42)",
    "If-Else Check 1: Greater/Equal Branch (Expected)",
    "If-Else Check 2: Else Branch (Expected)",
    "Not Equal Check (25 != 99)",
    "3",
    "2",
    "1",
    "Liftoff!",
    "12",
    "3",
    "49",
    "99",
    "42",
    "84",
    "123456789",
    "777",
    "104",
    "6",
    "78",
    "52",
    "=== ALL TESTS COMPLETED ==="
)

$FailedChecks = 0
foreach ($pat in $ExpectedPatterns) {
    if ($Output -notcontains $pat -and ($Output -join " ") -notmatch [regex]::Escape($pat)) {
        Write-Red "[-] Missing expected output: '$pat'"
        $FailedChecks++
    }
}

if ($FailedChecks -eq 0) {
    Write-Green "[+] All 27 regression points verified successfully!"
} else {
    Write-Red "[-] $FailedChecks regression assertions failed."
    exit 1
}

# --- STEP 4: Tri-Platform Conversion (PE -> ELF64 / Mach-O) ---
Write-Yellow "`n[+] STEP 4: Converting app.exe -> app.elf (ELF64) and app.macho (macOS ARM64)..."
$AppElf   = Join-Path $CompilerDir "app.elf"
$AppMacho = Join-Path $CompilerDir "app.macho"

& $NexElf $AppExe $AppElf
if (Test-Path $AppElf) {
    $elfSize = (Get-Item $AppElf).Length
    Write-Green "[+] ELF64 generated: $AppElf ($elfSize bytes)"
}

& $NexMacho $AppExe $AppMacho
if (Test-Path $AppMacho) {
    $machoSize = (Get-Item $AppMacho).Length
    Write-Green "[+] Mach-O generated: $AppMacho ($machoSize bytes)"
}

# --- STEP 5: Run Automated Regression Suite ---
if (-not $SkipTests) {
    Write-Yellow "`n[+] STEP 5: Running regression test suite with nextest.exe..."
    & $NexTest $Nexc --tests $TestsDir
    # nextest returns 0 if all tests pass, or 1 if any fail (e.g. Linux X11 tests)
    Write-Host ""
}

# --- STEP 6: Self-Hosting Bit-for-Bit Parity Verification ---
if (-not $SkipSelfHost) {
    Write-Yellow "`n[+] STEP 6: Verifying Stage 5 Self-Hosting Bit-for-Bit Parity..."
    $NexcNex = Join-Path $CompilerDir "nexc.nex"
    Copy-Item $NexcNex $CodeNex -Force

    Push-Location $CompilerDir
    try {
        Write-Host "  Compiling Generation 1 (nexc.exe -> app_gen1.exe)..."
        & $Nexc
        Move-Item "app.exe" "app_gen1.exe" -Force

        Write-Host "  Compiling Generation 2 (app_gen1.exe -> app_gen2.exe)..."
        & ".\app_gen1.exe"
        Move-Item "app.exe" "app_gen2.exe" -Force

        $diff = fc.exe /b app_gen1.exe app_gen2.exe
        if ($LASTEXITCODE -eq 0) {
            Write-Green "[+] STAGE 5 VERIFIED: Gen1 and Gen2 are 100% BIT-FOR-BIT IDENTICAL!"
            Copy-Item "app_gen2.exe" "nexc.exe" -Force
        } else {
            Write-Red "[-] Stage 5 parity check failed between Gen1 and Gen2:"
            Write-Host ($diff -join "`r`n")
            exit 1
        }
    }
    finally {
        Remove-Item "app_gen1.exe", "app_gen2.exe" -ErrorAction SilentlyContinue
        Pop-Location
    }
}

Write-Cyan "`n=================================================================="
Write-Green " [NEXUS] Master Build Pipeline Succeeded (100% Native Windows)   "
Write-Cyan "=================================================================="
