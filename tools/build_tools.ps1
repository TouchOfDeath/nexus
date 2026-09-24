# ==============================================================================
#                  NEXUS PROJECT: DEVELOPER TOOLKIT BUILD SCRIPT
#     Compiles all developer & debugging tools using Windows preinstalled csc.exe
# ==============================================================================
$ErrorActionPreference = "Stop"

$toolsDir = $PSScriptRoot
$cscPath  = "C:\Windows\Microsoft.NET\Framework64\v4.0.30319\csc.exe"

if (-not (Test-Path $cscPath)) {
    throw "Preinstalled csc.exe not found at $cscPath"
}

Write-Host "==================================================================" -ForegroundColor Cyan
Write-Host " [NEXUS] Building Developer & Debugging Toolkit Suite (v2 / v3)   " -ForegroundColor Cyan
Write-Host "==================================================================" -ForegroundColor Cyan
Write-Host "Using Windows Preinstalled C# Compiler: $cscPath`n" -ForegroundColor DarkGray

$tools = @(
    @{ Name = "nexpedump"; Source = "nexpedump.cs"; Desc = "PE32+ Header, Section & IAT Inspector                (v1)" },
    @{ Name = "nexdisasm"; Source = "nexdisasm.cs"; Desc = "x86-64 Machine Code Disassembler                    (v3)" },
    @{ Name = "nexdiff";   Source = "nexdiff.cs";   Desc = "Side-by-Side Binary & Section Diff Engine           (v2)" },
    @{ Name = "nexdebug";  Source = "nexdebug.cs";  Desc = "Native Win32 Debugger & Crash Interceptor           (v3)" },
    @{ Name = "nexfuzz";   Source = "nexfuzz.cs";   Desc = "Automated Grammar & Stress Test Fuzzer              (v2)" },
    @{ Name = "nexbench";  Source = "nexbench.cs";  Desc = "High-Precision Performance Benchmarker              (v2)" },
    @{ Name = "nextest";   Source = "nextest.cs";   Desc = "Compiler Regression Test Runner                     (v1)" }
)

$built  = 0
$errors = 0

foreach ($tool in $tools) {
    $src = Join-Path $toolsDir $tool.Source
    $out = Join-Path $toolsDir ($tool.Name + ".exe")

    Write-Host "[+] Compiling $($tool.Name).exe  -  $($tool.Desc)..." -ForegroundColor Yellow
    & $cscPath /nologo /optimize+ /target:exe /platform:x64 /out:$out $src 2>&1

    if ($LASTEXITCODE -ne 0) {
        Write-Host "    [-] FAILED to compile $($tool.Name).exe" -ForegroundColor Red
        $errors++
    } else {
        $len = (Get-Item $out).Length
        Write-Host "    -> Built: $(Split-Path $out -Leaf) ($("{0:N0}" -f $len) bytes)`n" -ForegroundColor Green
        $built++
    }
}

Write-Host "=================================================================="
if ($errors -eq 0) {
    Write-Host " [NEXUS] All $built tools built successfully!" -ForegroundColor Green
} else {
    Write-Host " [NEXUS] $built/$($tools.Count) tools built. $errors errors." -ForegroundColor Red
}
Write-Host "=================================================================="
