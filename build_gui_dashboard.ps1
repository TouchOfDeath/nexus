# ==============================================================================
#            NEXUS HYBRID SYSTEM: CONSOLE CALCULATION -> WIN32 GUI WINDOW
#            100% Native x86-64 Pipeline (0% C# / 0% .NET Runtime)
# ==============================================================================
$ErrorActionPreference = "Stop"

Write-Host "==================================================================" -ForegroundColor Cyan
Write-Host " [NEXUS HYBRID] Building Console Math -> Native Win32 Window      " -ForegroundColor Cyan
Write-Host "==================================================================" -ForegroundColor Cyan

$baseDir = $PSScriptRoot
$compilerDir = Join-Path $baseDir "compiler"
$guiDir = Join-Path $baseDir "gui_assembler"
$financeNex = Join-Path $baseDir "examples\finance.nex"

# Step 1: Copy financial program to compiler
Copy-Item $financeNex (Join-Path $compilerDir "code.nex") -Force

# Step 2: Compile finance.nex with nexc.exe
Write-Host "`n[+] STEP 1: Compiling finance.nex with nexc.exe..." -ForegroundColor Yellow
Push-Location $compilerDir
try {
    & .\nexc.exe
    if ($LASTEXITCODE -ne 0) { throw "Compilation failed!" }
    
    # Execute app.exe and capture console output
    $calcOutput = & .\app.exe
    $calcOutput | ForEach-Object { Write-Host $_ }
} finally {
    Pop-Location
}

# Step 3: Extract Net Profit from computation
$netProfit = $calcOutput[-3] # The computed number ($46,000)
Write-Host "`n[+] Computed Net Profit from NEXUS engine: `$$netProfit" -ForegroundColor Green

# Step 4: Generate input.txt for BASM GUI Assembler
$windowTitle = "NEXUS Financial Dashboard (0% C#)"
$windowBody = "Q3 Revenue: $90,000 | Net Profit: `$$netProfit | Status: Profitable"
$guiInput = "$windowTitle`r`n$windowBody`r`n"
Set-Content -Path (Join-Path $guiDir "input.txt") -Value $guiInput

# Step 5: Assemble native Win32 window with basm.exe
Write-Host "`n[+] STEP 2: Assembling native Win32 GUI window with basm.exe..." -ForegroundColor Yellow
Push-Location $guiDir
try {
    & .\basm.exe
    if ($LASTEXITCODE -ne 0) { throw "GUI Assembler failed!" }
} finally {
    Pop-Location
}

# Step 6: Launch the generated desktop window
Write-Host "`n[+] STEP 3: Launching output.exe (Native Win32 GUI Window)..." -ForegroundColor Yellow
Push-Location $guiDir
try {
    Start-Process -FilePath ".\output.exe"
    Write-Host "SUCCESS: Native Win32 window launched displaying computed results!" -ForegroundColor Green
} finally {
    Pop-Location
}

Write-Host "`n==================================================================" -ForegroundColor Cyan
Write-Host " End-to-End Hybrid Demonstration Complete!                        " -ForegroundColor Cyan
Write-Host "==================================================================" -ForegroundColor Cyan
