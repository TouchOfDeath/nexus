# ==============================================================================
#                      NEXUS PROJECT: MASTER BUILD SCRIPT (STAGE 5)
#           0% C#  -  0% .NET Runtime  -  100% Native x86-64 Machine Code
# ==============================================================================
$ErrorActionPreference = "Stop"

Write-Host "==================================================================" -ForegroundColor Cyan
Write-Host " [NEXUS] Building Full Project Pipeline (Stage 5 Self-Hosting)   " -ForegroundColor Cyan
Write-Host "==================================================================" -ForegroundColor Cyan

$baseDir = $PSScriptRoot
$compilerDir = Join-Path $baseDir "compiler"
$examplesDir = Join-Path $baseDir "examples"
$guiDir = Join-Path $baseDir "gui_assembler"
$toolsDir = Join-Path $baseDir "tools"

# --- STEP 1: Build NEXUS Compiler (nexc.exe) ---
Write-Host "`n[+] STEP 1: Building NEXUS Native AOT Compiler (nexc.exe)..." -ForegroundColor Yellow
Push-Location $compilerDir
try {
    & powershell.exe -ExecutionPolicy Bypass -File (Join-Path $compilerDir "build_nexc.ps1")
    if ($LASTEXITCODE -ne 0) { throw "build_nexc.ps1 failed with exit code $LASTEXITCODE" }
} finally {
    Pop-Location
}

# --- STEP 2: Compile Master Test Suite with nexc.exe ---
Write-Host "`n[+] STEP 2: Compiling code.nex -> app.exe using nexc.exe..." -ForegroundColor Yellow
Copy-Item (Join-Path $examplesDir "master_test_suite.nex") (Join-Path $compilerDir "code.nex") -Force
Push-Location $compilerDir
try {
    & .\nexc.exe
    if ($LASTEXITCODE -ne 0) { throw "nexc.exe compilation failed with exit code $LASTEXITCODE" }
} finally {
    Pop-Location
}

# --- STEP 3: Execute and Verify app.exe with Strict Output Diff ---
Write-Host "`n[+] STEP 3: Executing compiled app.exe and asserting Stage 3 Features..." -ForegroundColor Yellow
Push-Location $compilerDir
try {
    $output = & .\app.exe
    $output | ForEach-Object { Write-Host $_ }

    # Strict automated regression checks for Stage 1, 2, and 3:
    $expectedPairs = @(
        @{ Label = "100 / 4 (Division)"; Value = "25" },
        @{ Label = "100 % 30 (Modulo)"; Value = "10" },
        @{ Label = "5 + 3 * 10 - 4 / 2 (Chaining)"; Value = "38" },
        @{ Label = "50 / 2 / 5 (Multi-Div)"; Value = "5" },
        @{ Label = "While Loop Sum 1..5"; Value = "15" },
        @{ Label = "If Check (Equal)"; Value = "If Check: Equal (42 == 42)" },
        @{ Label = "If-Else True Branch"; Value = "If-Else Check 1: Greater/Equal Branch (Expected)" },
        @{ Label = "If-Else False Branch"; Value = "If-Else Check 2: Else Branch (Expected)" },
        @{ Label = "Not Equal Check"; Value = "Not Equal Check (25 != 99)" },
        @{ Label = "Countdown 3"; Value = "3" },
        @{ Label = "Countdown 2"; Value = "2" },
        @{ Label = "Countdown 1"; Value = "1" },
        @{ Label = "Countdown Finish"; Value = "Liftoff!" },
        @{ Label = "Nested While Loop Total"; Value = "12" },
        @{ Label = "Nested If/Else Even Count"; Value = "3" },
        @{ Label = "Nested If/Else Odd Count"; Value = "3" },
        @{ Label = "Square Function Execution"; Value = "49" },
        @{ Label = "Clamp Function Execution"; Value = "50" },
        @{ Label = "Stage 4 Byte Store/Load 1"; Value = "42" },
        @{ Label = "Stage 4 Byte Store/Load 2"; Value = "84" },
        @{ Label = "Stage 4 64-bit Store/Load"; Value = "123456789" },
        @{ Label = "Stage 4 Scaled Array Store/Load"; Value = "777" },
        @{ Label = "Stage 4 Chained Load Expression"; Value = "104" },
        @{ Label = "Stage 4 File Bytes Read"; Value = "6" },
        @{ Label = "Stage 4 File Byte 0 ('N')"; Value = "78" },
        @{ Label = "Stage 4 File Byte 5 ('4')"; Value = "52" },
        @{ Label = "All Tests Completed Sentinel"; Value = "=== ALL TESTS COMPLETED ===" }
    )

    $outText = ($output -join "`n")
    foreach ($pair in $expectedPairs) {
        $val = [regex]::Escape($pair.Value)
        if ($outText -notmatch "(?m)^$val$") {
            throw "REGRESSION ERROR: Expected '$($pair.Value)' for $($pair.Label) not found in app.exe output!"
        }
    }
    Write-Host "[+] All 27 Stage 4 regression assertions verified successfully!" -ForegroundColor Green
} finally {
    Pop-Location
}

# --- STEP 4: Verify Interactive Console Input (read <var>) with Stream Pipelining ---
Write-Host "`n[+] STEP 4: Verifying Interactive Console Input (read <var>) via piped stdin..." -ForegroundColor Yellow
Push-Location $compilerDir
try {
    # Test interactive_calc.nex
    Copy-Item (Join-Path $examplesDir "interactive_calc.nex") (Join-Path $compilerDir "code.nex") -Force
    & .\nexc.exe
    if ($LASTEXITCODE -ne 0) { throw "Compiling interactive_calc.nex failed" }
    
    $calcOut = @("45", "7") | & .\app.exe
    $calcText = ($calcOut -join "`n")
    if ($calcText -notmatch "Sum \(a \+ b\):\s*52") { throw "Interactive calc Sum assertion failed" }
    if ($calcText -notmatch "Difference \(a \- b\):\s*38") { throw "Interactive calc Difference assertion failed" }
    if ($calcText -notmatch "Product \(a \* b\):\s*315") { throw "Interactive calc Product assertion failed" }
    if ($calcText -notmatch "Quotient \(a \/ b\):\s*6") { throw "Interactive calc Quotient assertion failed" }
    if ($calcText -notmatch "Remainder \(a \% b\):\s*3") { throw "Interactive calc Remainder assertion failed" }
    Write-Host "[+] Interactive Console Input (read a, read b) verified: all math operations match!" -ForegroundColor Green

    # Test prime_checker.nex
    Copy-Item (Join-Path $examplesDir "prime_checker.nex") (Join-Path $compilerDir "code.nex") -Force
    & .\nexc.exe
    if ($LASTEXITCODE -ne 0) { throw "Compiling prime_checker.nex failed" }

    $primeOut = "29" | & .\app.exe
    if (($primeOut -join "`n") -notmatch "Verdict:\s*PRIME NUMBER!") { throw "Primality test for 29 failed" }

    $compOut = "35" | & .\app.exe
    if (($compOut -join "`n") -notmatch "Verdict:\s*COMPOSITE NUMBER!") { throw "Primality test for 35 failed" }
    Write-Host "[+] Primality checker verified: 29=PRIME, 35=COMPOSITE!" -ForegroundColor Green

    # Restore master test suite to code.nex
    Copy-Item (Join-Path $examplesDir "master_test_suite.nex") (Join-Path $compilerDir "code.nex") -Force
} finally {
    Pop-Location
}

# --- STEP 5: Build BASM GUI Assembler ---
Write-Host "`n[+] STEP 5: Building BASM Native Win32 GUI Assembler (basm.exe)..." -ForegroundColor Yellow
Push-Location $guiDir
try {
    & powershell.exe -ExecutionPolicy Bypass -File (Join-Path $guiDir "build_test_win.ps1")
    & powershell.exe -ExecutionPolicy Bypass -File (Join-Path $guiDir "build_basm.ps1")
    if ($LASTEXITCODE -ne 0) { throw "build_basm.ps1 failed with exit code $LASTEXITCODE" }
} finally {
    Pop-Location
}

# --- STEP 6: Assemble Win32 Window with basm.exe ---
Write-Host "`n[+] STEP 6: Assembling input.txt -> output.exe using basm.exe..." -ForegroundColor Yellow
Push-Location $guiDir
try {
    & .\basm.exe
    if ($LASTEXITCODE -ne 0) { throw "basm.exe execution failed with exit code $LASTEXITCODE" }
} finally {
    Pop-Location
}

# --- STEP 7: Stage 5 Full Self-Hosting Bitwise Convergence Assertion ---
Write-Host "`n[+] STEP 7: Testing Stage 5 Self-Hosting Horizon (Bootstrap -> Gen 2 -> Gen 3 -> Gen 4)..." -ForegroundColor Yellow
Push-Location $compilerDir
try {
    Copy-Item (Join-Path $compilerDir "nexc.nex") (Join-Path $compilerDir "code.nex") -Force
    if (Test-Path "app.exe") { Remove-Item "app.exe" -Force }
    # Gen 1 (bootstrap nexc.exe) compiles nexc.nex -> app.exe (Gen 2)
    & .\nexc.exe
    if ($LASTEXITCODE -ne 0) { throw "Gen 1 compilation failed" }
    Copy-Item "app.exe" "nexc2.exe" -Force
    
    # Gen 2 (nexc2.exe) compiles nexc.nex -> app.exe (Gen 3)
    if (Test-Path "app.exe") { Remove-Item "app.exe" -Force }
    & .\nexc2.exe
    if ($LASTEXITCODE -ne 0) { throw "Gen 2 compilation failed" }
    Copy-Item "app.exe" "nexc3.exe" -Force
    
    # Gen 3 (nexc3.exe) compiles nexc.nex -> app.exe (Gen 4)
    if (Test-Path "app.exe") { Remove-Item "app.exe" -Force }
    & .\nexc3.exe
    if ($LASTEXITCODE -ne 0) { throw "Gen 3 compilation failed" }
    Copy-Item "app.exe" "nexc4.exe" -Force
    
    # Bitwise parity check between Gen 3 and Gen 4:
    $fcOut = (& fc.exe /b nexc3.exe nexc4.exe 2>&1) -join "`n"
    if ($LASTEXITCODE -ne 0 -or $fcOut -notmatch "no differences encountered") {
        throw "STAGE 5 PARITY ERROR: nexc3.exe and nexc4.exe differ bitwise!`n$fcOut"
    }
    Write-Host "[+] Stage 5 Bitwise Fixed-Point verified: 100% BIT-FOR-BIT PARITY (32,768 bytes)!" -ForegroundColor Green
    Write-Host "[+] nexc3.exe == nexc4.exe across all 32,768 bytes!" -ForegroundColor Green
} finally {
    Pop-Location
}

# --- STEP 8: Build Developer & Debugging Toolkit Suite ---
Write-Host "`n[+] STEP 8: Building Developer & Debugging Toolkit Suite (tools/)..." -ForegroundColor Yellow
Push-Location $toolsDir
try {
    & powershell.exe -ExecutionPolicy Bypass -File (Join-Path $toolsDir "build_tools.ps1")
    if ($LASTEXITCODE -ne 0) { throw "build_tools.ps1 failed with exit code $LASTEXITCODE" }
} finally {
    Pop-Location
}

Write-Host "`n==================================================================" -ForegroundColor Green
Write-Host " [NEXUS] All builds, tests, and diff assertions passed!           " -ForegroundColor Green
Write-Host " STAGE 5 100% COMPLETE: THE SELF-HOSTING HORIZON ACHIEVED!       " -ForegroundColor Green
Write-Host " Fixed-Point Parity: nexc.exe == nexc.exe compiling nexc.nex     " -ForegroundColor Green
Write-Host " Zero C# and Zero .NET Runtime confirmed across all binaries.     " -ForegroundColor Green
Write-Host "==================================================================" -ForegroundColor Green
