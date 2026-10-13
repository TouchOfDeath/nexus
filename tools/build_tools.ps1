# ==============================================================================
#                  NEXUS PROJECT: DEVELOPER TOOLKIT BUILD SCRIPT (WINDOWS)
#     Compiles developer tools using Windows preinstalled csc.exe & bundled TCC
# ==============================================================================
$ErrorActionPreference = "Stop"

$toolsDir = $PSScriptRoot
$rootDir  = Split-Path $toolsDir -Parent
$binDir   = Join-Path $rootDir "bin"
$cscPath  = "C:\Windows\Microsoft.NET\Framework64\v4.0.30319\csc.exe"

if (-not (Test-Path $binDir)) {
    New-Item -ItemType Directory -Path $binDir -Force | Out-Null
}

Write-Host "==================================================================" -ForegroundColor Cyan
Write-Host " [NEXUS] Building Windows Developer & Compiler Toolkit Suite      " -ForegroundColor Cyan
Write-Host "==================================================================" -ForegroundColor Cyan

# ------------------------------------------------------------------------------
# 1. Compile C# Diagnostic and Debugging Tools with Windows csc.exe
# ------------------------------------------------------------------------------
if (Test-Path $cscPath) {
    Write-Host "`n[+] PART 1: Compiling C# Diagnostic Tools with Windows csc.exe..." -ForegroundColor Yellow
    $csTools = @(
        @{ Name = "nexpedump"; Source = "nexpedump.cs"; Desc = "PE32+ Header, Section & IAT Inspector" },
        @{ Name = "nexdisasm"; Source = "nexdisasm.cs"; Desc = "x86-64 Machine Code Disassembler" },
        @{ Name = "nexdiff";   Source = "nexdiff.cs";   Desc = "Side-by-Side Binary & Section Diff Engine" },
        @{ Name = "nexdebug";  Source = "nexdebug.cs";  Desc = "Native Win32 Debugger & Crash Interceptor" },
        @{ Name = "nexfuzz";   Source = "nexfuzz.cs";   Desc = "Automated Grammar & Stress Test Fuzzer" },
        @{ Name = "nexbench";  Source = "nexbench.cs";  Desc = "High-Precision Performance Benchmarker" },
        @{ Name = "nextest";   Source = "nextest.cs";   Desc = "Compiler Regression Test Runner" }
    )

    foreach ($tool in $csTools) {
        $src = Join-Path $toolsDir $tool.Source
        $out = Join-Path $toolsDir ($tool.Name + ".exe")
        $binOut = Join-Path $binDir ($tool.Name + ".exe")

        & $cscPath /nologo /optimize+ /target:exe /platform:x64 /out:$out $src 2>&1 | Out-Null
        if ($LASTEXITCODE -eq 0) {
            Copy-Item $out $binOut -Force
            $len = (Get-Item $out).Length
            $lenStr = "{0:N0}" -f $len
            Write-Host "    [OK] $($tool.Name).exe ($lenStr bytes) - $($tool.Desc)" -ForegroundColor Green
        } else {
            Write-Host "    [FAIL] Failed to compile $($tool.Name).exe" -ForegroundColor Red
        }
    }
}

# ------------------------------------------------------------------------------
# 2. Ensure Tiny C Compiler (TCC) is Available for Native C Binaries
# ------------------------------------------------------------------------------
Write-Host "`n[+] PART 2: Compiling Native C Toolchain Binaries (bin/*.exe)..." -ForegroundColor Yellow
$tccDir = Join-Path $toolsDir "tcc"
$tccExe = Join-Path $tccDir "tcc\tcc.exe"

if (-not (Test-Path $tccExe)) {
    Write-Host "    [!] Downloading bundled Tiny C Compiler for Windows..." -ForegroundColor DarkGray
    New-Item -ItemType Directory -Path $tccDir -Force | Out-Null
    $zipPath = Join-Path $tccDir "tcc.zip"
    Invoke-WebRequest -Uri "http://download.savannah.gnu.org/releases/tinycc/tcc-0.9.27-win64-bin.zip" -OutFile $zipPath -UseBasicParsing
    Expand-Archive -Path $zipPath -DestinationPath $tccDir -Force
    Remove-Item $zipPath -Force -ErrorAction SilentlyContinue
}

if (-not (Test-Path $tccExe)) {
    throw "TCC executable not found at $tccExe"
}

# ------------------------------------------------------------------------------
# 3. Compile Native C Tools into bin/
# ------------------------------------------------------------------------------
$cTools = @(
    @{ Name = "nexprep"; Source = "nexprep.c"; StackReserve = 67108864; Desc = "NEXUS AST & Preprocessor Engine" },
    @{ Name = "nexfmt";  Source = "nexfmt.c";  StackReserve = 0;        Desc = "Idempotent Source Code Formatter" },
    @{ Name = "nexelf";  Source = "nexelf.c";  StackReserve = 0;        Desc = "PE32+ to Linux ELF64 Converter" },
    @{ Name = "nexmacho";Source = "nexmacho.c";StackReserve = 0;        Desc = "PE32+ to macOS Mach-O Converter" },
    @{ Name = "nexarm64";Source = "nexarm64.c";StackReserve = 0;        Desc = "Native Apple Silicon ARM64 Emitter" },
    @{ Name = "nexir";   Source = "nexir.c";   StackReserve = 0;        Desc = "N-IR Intermediate Representation Engine" }
)

$patchScript = Join-Path $toolsDir "patch_stack.ps1"

foreach ($tool in $cTools) {
    $src = Join-Path $toolsDir $tool.Source
    $out = Join-Path $binDir ($tool.Name + ".exe")

    & $tccExe $src -o $out
    if ($LASTEXITCODE -eq 0) {
        if ($tool.StackReserve -gt 0 -and (Test-Path $patchScript)) {
            & powershell.exe -ExecutionPolicy Bypass -File $patchScript $out $tool.StackReserve | Out-Null
        }
        $len = (Get-Item $out).Length
        $lenStr = "{0:N0}" -f $len
        Write-Host "    [OK] $($tool.Name).exe ($lenStr bytes) - $($tool.Desc)" -ForegroundColor Green
    } else {
        Write-Host "    [FAIL] Failed to compile $($tool.Name).exe" -ForegroundColor Red
    }
}

Write-Host "`n==================================================================" -ForegroundColor Cyan
Write-Host " [NEXUS] All Windows developer & compiler tools ready in bin/     " -ForegroundColor Cyan
Write-Host "==================================================================" -ForegroundColor Cyan
